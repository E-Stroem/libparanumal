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

#include "ellipticHex3D.h"

typedef struct{

  dlong localId;
  hlong baseId;
  int haloFlag;

} preconGatherInfo_t;

int parallelCompareBaseId(const void *a, const void *b){

  preconGatherInfo_t *fa = (preconGatherInfo_t*) a;
  preconGatherInfo_t *fb = (preconGatherInfo_t*) b;

  if(fa->baseId < fb->baseId) return -1;
  if(fa->baseId > fb->baseId) return +1;

  return 0;
}

void ellipticSetupSmootherOverlappingPatch(solver_t *solver, precon_t *precon, agmgLevel *level,
                                              dfloat tau, dfloat lambda, int* BCType, const char *options) {

  mesh3D *mesh = solver->mesh;

  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // assumes meshParallelGatherScatterSetup3D has been called
  
  // ????? need to extend storage for halo ?????
  dlong Nlocal = mesh->Np*mesh->Nelements;
  dlong Nhalo  = mesh->Np*mesh->totalHaloPairs;
  dlong Ntrace = mesh->Nfp*mesh->Nfaces*mesh->Nelements;

  // offsets to extract second layer
  int *offset = (int*) calloc(mesh->Nfaces, sizeof(int));
  offset[0] = +mesh->Nfp;
  offset[1] = +mesh->Nq;
  offset[2] = -1;
  offset[3] = -mesh->Nq;
  offset[4] = +1;
  offset[5] = -mesh->Nfp;

  // build gather-scatter
  int NqP = mesh->Nq+2;
  int NpP = NqP*NqP*NqP;

  int *offsetP = (int*) calloc(mesh->Nfaces, sizeof(int));
  offsetP[0] = +NqP*NqP;
  offsetP[1] = +NqP;
  offsetP[2] = -1;
  offsetP[3] = -NqP;
  offsetP[4] = +1;
  offsetP[5] = -NqP*NqP;

  int *faceNodesPrecon = (int*) calloc(mesh->Nfp*mesh->Nfaces, sizeof(int));
  for(int j=0;j<mesh->Nq;++j)
    for(int i=0;i<mesh->Nq;++i)
      faceNodesPrecon[i+j*mesh->Nq+0*mesh->Nfp] = i+1 + (j+1)*NqP + 0*NqP*NqP;
  
  for(int k=0;k<mesh->Nq;++k)
    for(int i=0;i<mesh->Nq;++i)
      faceNodesPrecon[i+k*mesh->Nq+1*mesh->Nfp] = i+1 + 0*NqP + (k+1)*NqP*NqP;
  
  for(int k=0;k<mesh->Nq;++k)
    for(int j=0;j<mesh->Nq;++j)
      faceNodesPrecon[j+k*mesh->Nq+2*mesh->Nfp] = NqP-1 + (j+1)*NqP + (k+1)*NqP*NqP;

  for(int k=0;k<mesh->Nq;++k)
    for(int i=0;i<mesh->Nq;++i)
      faceNodesPrecon[i+k*mesh->Nq+3*mesh->Nfp] = i+1 + (NqP-1)*NqP + (k+1)*NqP*NqP;

  for(int k=0;k<mesh->Nq;++k)
    for(int j=0;j<mesh->Nq;++j)
      faceNodesPrecon[j+k*mesh->Nq+4*mesh->Nfp] = 0 + (j+1)*NqP + (k+1)*NqP*NqP;
  
  for(int j=0;j<mesh->Nq;++j)
    for(int i=0;i<mesh->Nq;++i)
      faceNodesPrecon[i+j*mesh->Nq+5*mesh->Nfp] = i+1 + (j+1)*NqP + (NqP-1)*NqP*NqP;

  dlong *vmapMP = (dlong*) calloc(mesh->Nfp*mesh->Nfaces*mesh->Nelements, sizeof(dlong));
  dlong *vmapPP = (dlong*) calloc(mesh->Nfp*mesh->Nfaces*mesh->Nelements, sizeof(dlong));
  
  // take node info from positive trace and put in overlap region on parallel gather info
  for(dlong e=0;e<mesh->Nelements;++e){

    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){
      dlong fid = e*mesh->Nfp*mesh->Nfaces + n;

      // find face index
      int fM = n/mesh->Nfp;
      int fP = mesh->EToF[e*mesh->Nfaces+fM];
      if(fP<0) fP = fM;

      // find location of "+" trace in regular mesh
      dlong idM = mesh->vmapM[fid] + offset[fM];
      dlong idP = mesh->vmapP[fid] + offset[fP];

      vmapMP[fid] = idM;
      vmapPP[fid] = idP;
    }
  }
  
  // space for gather base indices with halo
  preconGatherInfo_t *gatherInfo =
    (preconGatherInfo_t*) calloc(Nlocal+Nhalo, sizeof(preconGatherInfo_t));

  // rearrange in node order
  for(dlong n=0;n<Nlocal;++n){
    dlong id = mesh->gatherLocalIds[n];
    gatherInfo[id].baseId   = mesh->gatherBaseIds[n] + 1;
    gatherInfo[id].haloFlag = mesh->gatherHaloFlags[n];
  }

  if(Nhalo){
    // send buffer for outgoing halo
    preconGatherInfo_t *sendBuffer = (preconGatherInfo_t*) calloc(Nhalo, sizeof(preconGatherInfo_t));
    
    meshHaloExchange(mesh,
         mesh->Np*sizeof(preconGatherInfo_t),
         gatherInfo,
         sendBuffer,
         gatherInfo+Nlocal);
  }
  // now create padded version

  // now find info about halo nodes
  preconGatherInfo_t *preconGatherInfo = 
    (preconGatherInfo_t*) calloc(NpP*mesh->Nelements, sizeof(preconGatherInfo_t));

  // push to non-overlap nodes
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int k=0;k<mesh->Nq;++k){
      for(int j=0;j<mesh->Nq;++j){
        for(int i=0;i<mesh->Nq;++i){
          dlong id  = i + j*mesh->Nq + k*mesh->Nq*mesh->Nq + e*mesh->Np;
          dlong pid = i + 1 + (j+1)*NqP + (k+1)*NqP*NqP + e*NpP;

          preconGatherInfo[pid] = gatherInfo[id];
        }
      }
    }
  }

  // add overlap region
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){
      dlong id = n + e*mesh->Nfp*mesh->Nfaces;

      int f = n/mesh->Nfp;
      dlong idM = mesh->vmapM[id];
      dlong idP = vmapPP[id];
      dlong idMP = e*NpP + faceNodesPrecon[n];
      
      preconGatherInfo[idMP] = gatherInfo[idP];

      if(gatherInfo[idM].haloFlag){
        preconGatherInfo[idMP].haloFlag = 1;
        preconGatherInfo[idMP+offsetP[f]].haloFlag = 1;
        preconGatherInfo[idMP+2*offsetP[f]].haloFlag = 1;
      }
    }
  }
  
  // reset local ids
  for(dlong n=0;n<mesh->Nelements*NpP;++n)
    preconGatherInfo[n].localId = n;

#if 0
  char fname[BUFSIZ];
  sprintf(fname, "haloFlag%05d.dat", rank);
  FILE *fp = fopen(fname, "w");
  
  for(int p=0;p<size;++p){
    if(p==rank){
      for(int e=0;e<mesh->Nelements;++e){
  fprintf(fp,"e=%d: \n", e);
  for(int k=0;k<mesh->NqP;++k){
    for(int j=0;j<mesh->NqP;++j){
      for(int i=0;i<mesh->NqP;++i){
        int id = i + mesh->NqP*j + mesh->NqP*mesh->NqP*k+ e*NpP;
        fprintf(fp,"%d ", preconGatherInfo[id].haloFlag);
      }
      fprintf(fp,"\n");
    }
    fprintf(fp,"\n");
  }
  fprintf(fp,"\n");
      }
    }
  }
  fclose(fp);
#endif

  // sort by rank then base index
  qsort(preconGatherInfo, NpP*mesh->Nelements, sizeof(preconGatherInfo_t), parallelCompareBaseId);
  
  // do not gather-scatter nodes labelled zero
  dlong skip = 0;
  while(preconGatherInfo[skip].baseId==0 && skip<NpP*mesh->Nelements){
    ++skip;
  }
  //printf("skip = %d out of %d\n", skip, NpP*mesh->Nelements);

  // reset local ids
  dlong NlocalP = NpP*mesh->Nelements - skip;
  dlong *gatherLocalIdsP  = (dlong*) calloc(NlocalP, sizeof(dlong));
  hlong *gatherBaseIdsP   = (hlong*) calloc(NlocalP, sizeof(hlong));
  int *gatherHaloFlagsP = (int*) calloc(NlocalP, sizeof(int));
  for(dlong n=0;n<NlocalP;++n){
    gatherLocalIdsP[n]  = preconGatherInfo[n+skip].localId;
    gatherBaseIdsP[n]   = preconGatherInfo[n+skip].baseId;
    gatherHaloFlagsP[n] = preconGatherInfo[n+skip].haloFlag;
  }

  // make preconBaseIds => preconNumbering
  /* depreciated */
  // precon->ogsP = meshParallelGatherScatterSetup(mesh,
  //           NlocalP,
  //           sizeof(dfloat),
  //           gatherLocalIdsP,
  //           gatherBaseIdsP,
  //           gatherHaloFlagsP);

  // build degree vector
  dlong NtotalP = mesh->NqP*mesh->NqP*mesh->NqP*mesh->Nelements;
  dfloat *invDegree = (dfloat*) calloc(NtotalP, sizeof(dfloat));
  dfloat *degree    = (dfloat*) calloc(NtotalP, sizeof(dfloat));
  precon->o_invDegreeP = mesh->device.malloc(NtotalP*sizeof(dfloat), invDegree);
  
  for(dlong n=0;n<NtotalP;++n)
    degree[n] = 1;

  occa::memory o_deg = mesh->device.malloc(NtotalP*sizeof(dfloat), degree);
  meshParallelGatherScatter(mesh, precon->ogsP, o_deg);
  o_deg.copyTo(degree);
  mesh->device.finish();
  o_deg.free();

  for(dlong n=0;n<NtotalP;++n){ // need to weight inner products{
    if(degree[n] == 0) printf("WARNING!!!!\n");
    invDegree[n] = 1./degree[n];
  }
  
  precon->o_invDegreeP.copyFrom(invDegree);
  free(degree);
  free(invDegree);

  // -------------------------------------------------------------------------------------------
  // build gather-scatter for overlapping patches
  dlong *allNelements = (int*) calloc(size, sizeof(int));
  MPI_Allgather(&(mesh->Nelements), 1, MPI_DLONG,
                      allNelements, 1, MPI_DLONG, MPI_COMM_WORLD);

  // offsets
  hlong *startElement = (hlong*) calloc(size, sizeof(hlong));
  for(int r=1;r<size;++r){
    startElement[r] = startElement[r-1]+allNelements[r-1];
  }

  // 1-indexed numbering of nodes on this process
  hlong *localNums = (hlong*) calloc((Nlocal+Nhalo), sizeof(hlong));
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      localNums[e*mesh->Np+n] = 1 + e*mesh->Np + n + startElement[rank]*mesh->Np;
    }
  }
  
  if(Nhalo){
    // send buffer for outgoing halo
    hlong *sendBuffer = (hlong*) calloc(Nhalo, sizeof(hlong));

    // exchange node numbers with neighbors
    meshHaloExchange(mesh,
         mesh->Np*sizeof(hlong),
         localNums,
         sendBuffer,
         localNums+Nlocal);
  }
  
  preconGatherInfo_t *preconGatherInfoDg = 
    (preconGatherInfo_t*) calloc(NpP*mesh->Nelements,
         sizeof(preconGatherInfo_t));

  // set local ids
  for(dlong n=0;n<mesh->Nelements*NpP;++n)
    preconGatherInfoDg[n].localId = n;

  // numbering of patch interior nodes
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int k=0;k<mesh->Nq;++k){
      for(int j=0;j<mesh->Nq;++j){
        for(int i=0;i<mesh->Nq;++i){
          dlong id  = i + j*mesh->Nq + k*mesh->Nq*mesh->Nq + e*mesh->Np;
          dlong pid = (i+1) + (j+1)*NqP + (k+1)*NqP*NqP + e*NpP;

          // all patch interior nodes are local
          preconGatherInfoDg[pid].baseId = localNums[id];
        }
      }
    }
  }
  // add patch boundary nodes
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int f=0;f<mesh->Nfaces;++f){
      // mark halo nodes
      int rP = mesh->EToP[e*mesh->Nfaces+f];
      dlong eP = mesh->EToE[e*mesh->Nfaces+f];
      int fP = mesh->EToF[e*mesh->Nfaces+f];
      int bc = mesh->EToB[e*mesh->Nfaces+f];
      
      for(int n=0;n<mesh->Nfp;++n){
        dlong id = n + f*mesh->Nfp+e*mesh->Nfp*mesh->Nfaces;
        dlong idP = mesh->vmapP[id];
        
        // local numbers
        dlong pidM = e*NpP + faceNodesPrecon[f*mesh->Nfp+n] + offsetP[f]; 
        dlong pidP = e*NpP + faceNodesPrecon[f*mesh->Nfp+n];
        preconGatherInfoDg[pidP].baseId = localNums[idP];
        
        if(rP!=-1){
          preconGatherInfoDg[pidM].haloFlag = 1;
          preconGatherInfoDg[pidP].haloFlag = 1;
        }
      }
    }
  }

  // sort by rank then base index
  qsort(preconGatherInfoDg, NpP*mesh->Nelements, sizeof(preconGatherInfo_t),
  parallelCompareBaseId);
    
  // do not gather-scatter nodes labelled zero
  skip = 0;

  while(preconGatherInfoDg[skip].baseId==0 && skip<NpP*mesh->Nelements){
    ++skip;
  }

  // reset local ids
  dlong NlocalDg = NpP*mesh->Nelements - skip;
  dlong *gatherLocalIdsDg  = (dlong*) calloc(NlocalDg, sizeof(dlong));
  hlong *gatherBaseIdsDg   = (hlong*) calloc(NlocalDg, sizeof(hlong));
  int *gatherHaloFlagsDg = (int*) calloc(NlocalDg, sizeof(int));
  for(dlong n=0;n<NlocalDg;++n){
    gatherLocalIdsDg[n]  = preconGatherInfoDg[n+skip].localId;
    gatherBaseIdsDg[n]   = preconGatherInfoDg[n+skip].baseId;
    gatherHaloFlagsDg[n] = preconGatherInfoDg[n+skip].haloFlag;
  }

  // make preconBaseIds => preconNumbering
  /* depreciated */
  // precon->ogsDg = meshParallelGatherScatterSetup(mesh,
  //            NlocalDg,
  //            sizeof(dfloat),
  //            gatherLocalIdsDg,
  //            gatherBaseIdsDg,
  //            gatherHaloFlagsDg);
  
  // build degree vector
  dlong NtotalDGP = NpP*mesh->Nelements;
  invDegree = (dfloat*) calloc(NtotalDGP, sizeof(dfloat));
  degree    = (dfloat*) calloc(NtotalDGP, sizeof(dfloat));
  precon->o_invDegreeDGP = mesh->device.malloc(NtotalDGP*sizeof(dfloat), invDegree);
  
  for(dlong n=0;n<NtotalDGP;++n)
    degree[n] = 1;

  o_deg = mesh->device.malloc(NtotalDGP*sizeof(dfloat), degree);
  meshParallelGatherScatter(mesh, precon->ogsDg, o_deg);
  o_deg.copyTo(degree);
  mesh->device.finish();
  o_deg.free();

  for(int n=0;n<NtotalDGP;++n){ // need to weight inner products{
    if(degree[n] == 0) printf("WARNING!!!!\n");
    invDegree[n] = 1./degree[n];
  }
  
  precon->o_invDegreeDGP.copyFrom(invDegree);
  free(degree);
  free(invDegree);

  // -------------------------------------------------------------------------------------------
  

  
  precon->o_faceNodesP = mesh->device.malloc(mesh->Nfp*mesh->Nfaces*sizeof(int), faceNodesPrecon);
  precon->o_vmapPP     = mesh->device.malloc(mesh->Nfp*mesh->Nfaces*mesh->Nelements*sizeof(int), vmapPP);

  precon->o_oasForward = mesh->device.malloc(NqP*NqP*sizeof(dfloat), mesh->oasForward);
  precon->o_oasBack    = mesh->device.malloc(NqP*NqP*sizeof(dfloat), mesh->oasBack);

  precon->o_oasForwardDg = mesh->device.malloc(NqP*NqP*sizeof(dfloat), mesh->oasForwardDg);
  precon->o_oasBackDg    = mesh->device.malloc(NqP*NqP*sizeof(dfloat), mesh->oasBackDg);
  
  /// ---------------------------------------------------------------------------
  
  // hack estimate for Jacobian scaling
  dfloat *diagInvOp = (dfloat*) calloc(NpP*mesh->Nelements, sizeof(dfloat));
  dfloat *diagInvOpDg = (dfloat*) calloc(NpP*mesh->Nelements, sizeof(dfloat));
            
  for(dlong e=0;e<mesh->Nelements;++e){

    // S = Jabc*(wa*wb*wc*lambda + wb*wc*Da'*wa*Da + wa*wc*Db'*wb*Db + wa*wb*Dc'*wc*Dc)
    // S = Jabc*wa*wb*wc*(lambda*I+1/wa*Da'*wa*Da + 1/wb*Db'*wb*Db + 1/wc*Dc'*wc*Dc)
    
    dfloat Jhrinv2 = 0, Jhsinv2 = 0, Jhtinv2 = 0, J = 0;
    for(int n=0;n<mesh->Np;++n){
      dfloat W = mesh->gllw[n%mesh->Nq]*
      mesh->gllw[(n/mesh->Nq)%mesh->Nq]*
      mesh->gllw[n/(mesh->Nq*mesh->Nq)];
      dlong base = mesh->Nggeo*mesh->Np*e + n;

      J = mymax(J, mesh->ggeo[base + mesh->Np*GWJID]/W);
      Jhrinv2 = mymax(Jhrinv2, mesh->ggeo[base + mesh->Np*G00ID]/W);
      Jhsinv2 = mymax(Jhsinv2, mesh->ggeo[base + mesh->Np*G11ID]/W);
      Jhtinv2 = mymax(Jhtinv2, mesh->ggeo[base + mesh->Np*G22ID]/W);
      
    }
    
    for(int k=0;k<NqP;++k){
      for(int j=0;j<NqP;++j){
        for(int i=0;i<NqP;++i){
          dlong pid = i + j*NqP + k*NqP*NqP + e*NpP;
          
          diagInvOp[pid] =
            1./(J*lambda +
          Jhrinv2*mesh->oasDiagOp[i] +
          Jhsinv2*mesh->oasDiagOp[j] +
          Jhtinv2*mesh->oasDiagOp[k]);


          diagInvOpDg[pid] =
            1./(J*lambda +
          Jhrinv2*mesh->oasDiagOpDg[i] +
          Jhsinv2*mesh->oasDiagOpDg[j] +
          Jhtinv2*mesh->oasDiagOpDg[k]);

        }
      }
    }
  }
  
  precon->o_oasDiagInvOp = mesh->device.malloc(NpP*mesh->Nelements*sizeof(dfloat), diagInvOp);
  precon->o_oasDiagInvOpDg = mesh->device.malloc(NpP*mesh->Nelements*sizeof(dfloat), diagInvOpDg);
  
  /// ---------------------------------------------------------------------------
  // compute diagonal of stiffness matrix for Jacobi

  dlong Ntotal = mesh->Np*mesh->Nelements;
  dfloat *diagA = (dfloat*) calloc(Ntotal, sizeof(dfloat));
           
  for(dlong e=0;e<mesh->Nelements;++e){
    dlong cnt = 0;
    for(int k=0;k<mesh->Nq;++k){
      for(int j=0;j<mesh->Nq;++j){
        for(int i=0;i<mesh->Nq;++i){
          
          dfloat JW = mesh->ggeo[e*mesh->Np*mesh->Nggeo+ cnt + mesh->Np*GWJID];
          // (D_{ii}^2 + D_{jj}^2 + D_{kk}^2 + lambda)*w_i*w_j*w_k*J_{ijke}
          diagA[e*mesh->Np+cnt] = (pow(mesh->D[i*mesh->Nq+i],2) +
                                   pow(mesh->D[j*mesh->Nq+j],2) +
                                   pow(mesh->D[k*mesh->Nq+k],2) +
                                   lambda)*JW;
          ++cnt;
        }
      }
    }
  }

  precon->o_diagA = mesh->device.malloc(Ntotal*sizeof(dfloat), diagA);

  // sum up
  meshParallelGatherScatter(mesh, mesh->ogs, precon->o_diagA);

  free(diagA);

  level->device_smoother = overlappingPatchIpdg;

  //check if stabilization is needed
  if (strstr(options,"MULTIGRID")||strstr(options,"FULLALMOND")) {
    //estimate the max eigenvalue of S*A
    dfloat rho = maxEigSmoothAx(solver, level, options);

    if (strstr(options,"CHEBYSHEV")) {

      level->smoother_params = (dfloat *) calloc(2,sizeof(dfloat));

      level->ChebyshevIterations = 2;
      level->smoother_params[0] = rho;
      level->smoother_params[1] = rho/10.;

    } else {

      //set the stabilty weight (jacobi-type interation)
      dfloat weight = (4./3.)/rho;

      for (dlong n=0;n<NpP*mesh->Nelements;n++)
        diagInvOpDg[n] *= weight;

      //update diagonal with weight
      precon->o_oasDiagInvOpDg.copyFrom(diagInvOpDg);
    }
  }
}

void ellipticSetupSmootherLocalPatch(solver_t *solver, precon_t *precon, agmgLevel *level,
                                              dfloat tau, dfloat lambda, int* BCType, dfloat rateTolerance, const char *options) {

  dfloat *invAP;
  dlong Npatches;
  dlong *patchesIndex;
  mesh_t *mesh = solver->mesh;

  int NpP = mesh->Np;

  //initialize the full inverse operators on each 4 element patch
  ellipticBuildLocalPatchesHex3D(solver, tau, lambda, BCType, rateTolerance,
                                      &Npatches, &patchesIndex, &invAP, options);

  precon->o_invAP = mesh->device.malloc(Npatches*NpP*NpP*sizeof(dfloat),invAP);
  precon->o_patchesIndex = mesh->device.malloc(mesh->Nelements*sizeof(dlong), patchesIndex);

  dfloat *invDegree = (dfloat*) calloc(mesh->Nelements,sizeof(dfloat));
  for (dlong e=0;e<mesh->Nelements;e++) {
    invDegree[e] = 1.0;
  }
  precon->o_invDegreeAP = mesh->device.malloc(mesh->Nelements*sizeof(dfloat),invDegree);

  level->device_smoother = LocalPatchIpdg;

  //check if stabilization is needed
  if (strstr(options,"MULTIGRID")||strstr(options,"FULLALMOND")) {
    //estimate the max eigenvalue of S*A
    dfloat rho = maxEigSmoothAx(solver, level, options);

    if (strstr(options,"CHEBYSHEV")) {

      level->smoother_params = (dfloat *) calloc(2,sizeof(dfloat));

      level->ChebyshevIterations = 2;
      level->smoother_params[0] = rho;
      level->smoother_params[1] = rho/10.;

    } else {

      //set the stabilty weight (jacobi-type interation)
      dfloat weight = (4./3.)/rho;

      for (dlong e=0;e<mesh->Nelements;e++)
        invDegree[e] *= weight;

      //update with weight
      precon->o_invDegreeAP.copyFrom(invDegree);
    }
  }
  free(invDegree);
}

void ellipticSetupSmootherDampedJacobi(solver_t *solver, precon_t *precon, agmgLevel *level,
                                              dfloat tau, dfloat lambda, int* BCType, const char *options) {

  dfloat *invDiagA;
  mesh_t *mesh = solver->mesh;

  ellipticBuildJacobiHex3D(solver,tau, lambda, BCType, &invDiagA,options);

  precon->o_invDiagA = mesh->device.malloc(mesh->Np*mesh->Nelements*sizeof(dfloat), invDiagA);

  level->device_smoother = dampedJacobi;

  //check if stabilization is needed
  if (strstr(options,"MULTIGRID")||strstr(options,"FULLALMOND")) {
    //estimate the max eigenvalue of S*A
    dfloat rho = maxEigSmoothAx(solver, level, options);

    if (strstr(options,"CHEBYSHEV")) {

      level->smoother_params = (dfloat *) calloc(2,sizeof(dfloat));

      level->ChebyshevIterations = 2;
      level->smoother_params[0] = rho;
      level->smoother_params[1] = rho/10.;

    } else {

      //set the stabilty weight (jacobi-type interation)
      dfloat weight = (4./3.)/rho;

      for (dlong n=0;n<mesh->Np*mesh->Nelements;n++)
        invDiagA[n] *= weight;

      //update diagonal with weight
      precon->o_invDiagA.copyFrom(invDiagA);
    }
  }

  free(invDiagA);
}

static void eig(const int Nrows, double *A, double *WR, double *WI){

  int NB  = 256;
  char JOBVL  = 'V';
  char JOBVR  = 'V';
  int     N = Nrows;
  int   LDA = Nrows;
  int  LWORK  = (NB+2)*N;

  double *WORK  = new double[LWORK];
  double *VL  = new double[Nrows*Nrows];
  double *VR  = new double[Nrows*Nrows];

  int INFO = -999;

  dgeev_ (&JOBVL, &JOBVR, &N, A, &LDA, WR, WI,
    VL, &LDA, VR, &LDA, WORK, &LWORK, &INFO);


  assert(INFO == 0);

  delete [] VL;
  delete [] VR;
  delete [] WORK;
}

dfloat maxEigSmoothAx(solver_t* solver, agmgLevel *level,const char* options){

  mesh_t *mesh = solver->mesh;

  const dlong N = level->Nrows;
  const dlong M = level->Ncols;

  int k = 10;

  int rank, size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  hlong Nlocal = (hlong) level->Nrows;
  hlong Ntotal = 0;
  MPI_Allreduce(&Nlocal, &Ntotal, 1, MPI_HLONG, MPI_SUM, MPI_COMM_WORLD);
  if(k > Ntotal) k = (int) Ntotal;

  // do an arnoldi

  // allocate memory for Hessenberg matrix
  double *H = (double *) calloc(k*k,sizeof(double));

  // allocate memory for basis
  dfloat *Vx = (dfloat*) calloc(M, sizeof(dfloat));
  occa::memory *o_V = (occa::memory *) calloc(k+1, sizeof(occa::memory));
  
  occa::memory o_Vx  = mesh->device.malloc(M*sizeof(dfloat),Vx);
  occa::memory o_AVx = mesh->device.malloc(M*sizeof(dfloat),Vx);

  for(int i=0; i<=k; i++)
    o_V[i] = mesh->device.malloc(M*sizeof(dfloat),Vx);

  // generate a random vector for initial basis vector
  for (dlong i=0;i<N;i++) Vx[i] = (dfloat) drand48(); 

  //gather-scatter 
  if (strstr(options,"CONTINUOUS")) {
    gsParallelGatherScatter(mesh->hostGsh, Vx, dfloatString, "add"); 
    for (dlong i=0;i<solver->Nmasked;i++) Vx[solver->maskIds[i]] = 0.;
  }

  o_Vx.copyFrom(Vx); //copy to device
  dfloat norm_vo = ellipticWeightedInnerProduct(solver, solver->o_invDegree, o_Vx, o_Vx, options);
  norm_vo = sqrt(norm_vo);

  ellipticScaledAdd(solver, 1./norm_vo, o_Vx, 0. , o_V[0]);

  for(int j=0; j<k; j++){
    // v[j+1] = invD*(A*v[j])
    level->device_Ax(level->AxArgs,o_V[j],o_AVx);
    level->device_smoother(level->smootherArgs, o_AVx, o_V[j+1]);

    // modified Gram-Schmidth
    for(int i=0; i<=j; i++){
      // H(i,j) = v[i]'*A*v[j]
      dfloat hij = ellipticWeightedInnerProduct(solver, solver->o_invDegree, o_V[i], o_V[j+1], options);

      // v[j+1] = v[j+1] - hij*v[i]
      ellipticScaledAdd(solver, -hij, o_V[i], 1., o_V[j+1]);

      H[i + j*k] = (double) hij;
    }

    if(j+1 < k){
      // v[j+1] = v[j+1]/||v[j+1]||
      dfloat norm_vj = ellipticWeightedInnerProduct(solver, solver->o_invDegree, o_V[j+1], o_V[j+1], options);
      norm_vj = sqrt(norm_vj);
      ellipticScaledAdd(solver, 1/norm_vj, o_V[j+1], 0., o_V[j+1]);
      
      H[j+1+ j*k] = (double) norm_vj;
    }
  }

  double *WR = (double *) calloc(k,sizeof(double));
  double *WI = (double *) calloc(k,sizeof(double));

  eig(k, H, WR, WI);

  double rho = 0.;

  for(int i=0; i<k; i++){
    double rho_i  = sqrt(WR[i]*WR[i] + WI[i]*WI[i]);

    if(rho < rho_i) {
      rho = rho_i;
    }
  }

  // free memory
  free(H);
  free(WR);
  free(WI);

  free(Vx);
  o_Vx.free();
  o_AVx.free();
  for(int i=0; i<=k; i++) o_V[i].free();
  free((void*)o_V);

  if((rank==0)&&(strstr(options,"VERBOSE"))) printf("weight = %g \n", rho);

  return rho;
}
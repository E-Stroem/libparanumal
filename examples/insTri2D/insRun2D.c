#include "ins2D.h"

void insRun2D(ins_t *ins, char *options){

  mesh2D *mesh = ins->mesh; 
  // Write Initial Data
  insReport2D(ins, 0, options);
  // Allocate MPI buffer for velocity step solver!! May Change Later!!!!!!
  iint helmholtzHaloBytes = mesh->totalHaloPairs*mesh->Np*(ins->NTfields)*sizeof(dfloat);
  dfloat *helmholtzSendBuffer = (dfloat*) malloc(helmholtzHaloBytes);
  dfloat *helmholtzRecvBuffer = (dfloat*) malloc(helmholtzHaloBytes);
  
  //
  iint poissonHaloBytes = mesh->totalHaloPairs*mesh->Np*(ins->NVfields)*sizeof(dfloat);
  dfloat *poissonSendBuffer = (dfloat*) malloc(poissonHaloBytes);
  dfloat *poissonRecvBuffer = (dfloat*) malloc(poissonHaloBytes);

  // No need to do like this, just for consistency 
  iint updateHaloBytes = mesh->totalHaloPairs*mesh->Np*sizeof(dfloat);
  dfloat *updateSendBuffer = (dfloat*) malloc(updateHaloBytes);
  dfloat *updateRecvBuffer = (dfloat*) malloc(updateHaloBytes);

  occa::initTimer(mesh->device);
  
  for(iint tstep=0;tstep<1000;++tstep){

    switch(tstep){
    case 0:
      ins->a0 = 1.f, ins->b0 = 1.f;
      ins->a1 = 0.f, ins->b1 = 0.f;
      ins->a2 = 0.f, ins->b2 = 0.f;
      ins->g0 = 1.f; 
      break;

    case 1:
      ins->a0 =  2.f,  ins->b0  = 2.0f;
      ins->a1 = -0.5f, ins->b1 = -1.0f;
      ins->a2 =  0.f,  ins->b2  = 0.f;
      ins->g0 =  1.5f;
      break;
#if 1
    case 2:
      ins->a0 =  3.f,       ins->b0  =  3.0f;
      ins->a1 = -1.5f,      ins->b1  = -3.0f;
      ins->a2 =  1.f/3.f,   ins->b2  =  1.0f;
      ins->g0 =  11.f/6.f; 
      break;
#endif
    }
    
    ins->lambda = ins->g0 / (ins->dt * ins->nu);
    
    insAdvectionStep2D(ins, tstep, helmholtzHaloBytes, helmholtzSendBuffer, helmholtzRecvBuffer, options);
    insHelmholtzStep2D(ins, tstep, helmholtzHaloBytes, helmholtzSendBuffer, helmholtzRecvBuffer, options);
    insPoissonStep2D(  ins, tstep, poissonHaloBytes  , poissonSendBuffer  , poissonRecvBuffer  , options);
    insUpdateStep2D(   ins, tstep, updateHaloBytes,    updateSendBuffer,    updateRecvBuffer, options);
    
    if(strstr(options, "REPORT")){
      if((tstep%ins->errorStep)==0){
	  insReport2D(ins, tstep,options);
      }
    }
  }
  
  
 //  // // For Final Time
  insReport2D(ins, ins->NtimeSteps,options);

 //  occa::printTimer();

  // // Deallocate Halo MPI storage
  free(helmholtzSendBuffer);
  free(helmholtzRecvBuffer);
  // //
  free(poissonSendBuffer);
  free(poissonRecvBuffer);
  // //
  free(updateSendBuffer);
  free(updateRecvBuffer);
  //

}




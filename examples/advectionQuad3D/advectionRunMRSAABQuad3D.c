#include "advectionQuad3D.h"

void advectionRunMRSAABQuad3D(solver_t *solver){

  mesh_t *mesh = solver->mesh;
  
  occa::initTimer(mesh->device);
  
  // MPI send buffer
  iint haloBytes = mesh->totalHaloPairs*mesh->Np*mesh->Nfields*sizeof(dfloat);
  dfloat *sendBuffer = (dfloat*) malloc(haloBytes);
  dfloat *recvBuffer = (dfloat*) malloc(haloBytes);

  dfloat * test_q = (dfloat *) calloc(mesh->Nelements*mesh->Np*mesh->Nfields*mesh->Nrhs,sizeof(dfloat));
  
  /*for (int e = 0; e < mesh->Nelements; ++e) {
    for (int f = 0; f < mesh->Nfaces; ++f) {
    printf("%d ",mesh->EToF[e*mesh->Nfaces + f]);
    }
    printf("\n");
    }*/
  
  //kernel arguments
  dfloat alpha = 1./mesh->N;
  
  for(iint tstep=mesh->Nrhs;tstep<mesh->NtimeSteps;++tstep){
    
    for (iint Ntick=0; Ntick < pow(2,mesh->MRABNlevels-1);Ntick++) {
      
      iint mrab_order=mesh->Nrhs-1;
      /*      if (tstep - mesh->Nrhs == 0) mrab_order = 0;
      else if (tstep - mesh->Nrhs == 1) mrab_order = 1;
      else mrab_order = 2;*/
      
      //synthesize actual stage time
      iint t = tstep*pow(2,mesh->MRABNlevels-1) + Ntick;

      iint lev;
      for (lev=0;lev<mesh->MRABNlevels;lev++)
	if (Ntick % (1<<lev) != 0) break;

      iint levS;
      for (levS=0;levS<mesh->MRABNlevels;levS++)
        if ((Ntick+1) % (1<<levS) !=0) break; //find the max lev to update
      
      for (iint l=0;l<lev;l++) {
	if (mesh->MRABNelements[l]) {
	  // compute volume contribution to DG boltzmann RHS
	  mesh->volumeKernel(mesh->MRABNelements[l],
			     mesh->o_MRABelementIds[l],
			     mesh->MRABshiftIndex[l],
			     mesh->o_vgeo,
			     mesh->o_D,
			     mesh->o_x,
			     mesh->o_y,
			     mesh->o_z,
			     mesh->o_q,
			     mesh->o_rhsq);
	}
      }
      
      occa::tic("surfaceKernel");
      
           for (iint l=0;l<lev;l++) {
	if (mesh->MRABNelements[l]) {

	  mesh->surfaceKernel(mesh->MRABNelements[l],
			      mesh->o_MRABelementIds[l],
			      mesh->MRABshiftIndex[l],
			      mesh->o_sgeo,
			      mesh->o_LIFTT,
			      mesh->o_vmapM,
			      mesh->o_vmapP,
			      t,
			      mesh->o_x,
			      mesh->o_y,
			      mesh->o_z,
			      mesh->o_fQM,
			      mesh->o_rhsq);
	  mesh->lev_updates[l] = Ntick;
	}
	}
      occa::toc("surfaceKernel");
      
      mesh->o_shift.copyFrom(mesh->MRABshiftIndex);
      mesh->o_lev_updates.copyFrom(mesh->lev_updates);

      for (iint l = 0; l < lev; l++) {
		
      	mesh->filterKernelH(mesh->MRABNelements[l],
			    mesh->o_MRABelementIds[l],
			    mesh->o_shift,
			    mesh->o_dualProjMatrix,
			    mesh->o_cubeFaceNumber,
			    mesh->o_EToE,
			    mesh->o_lev_updates,
			    mesh->o_MRABlevels,
			    l,
			    mesh->o_rhsq,
			    mesh->o_qFilter);
      }
      for (iint l = 0; l < lev; l++) {
	
	mesh->filterKernelV(mesh->MRABNelements[l],
			    mesh->o_MRABelementIds[l],
			    mesh->o_shift,
			    alpha,
			    mesh->o_dualProjMatrix,
			    mesh->o_cubeFaceNumber,
			    mesh->o_EToE,
			    mesh->o_x,
			    mesh->o_y,
			    mesh->o_z,
			    mesh->o_lev_updates,
			    mesh->o_MRABlevels,
			    l,
			    mesh->o_rhsq,
			    mesh->o_qFilter,
			    mesh->o_qFiltered);	
			    }
       
      for (iint l=0;l<lev;l++) {
	if (mesh->MRABNelements[l]) {
	  mesh->volumeCorrectionKernel(mesh->MRABNelements[l],
				       mesh->o_MRABelementIds[l],
				       mesh->MRABshiftIndex[l],
				       mesh->o_q,
				       mesh->o_qCorr);
	}
      }
      
      for (iint l = 0; l < levS; l++) {
	const iint id = mrab_order*mesh->MRABNlevels*mesh->Nrhs + l*mesh->Nrhs;
	occa::tic("updateKernel");
	
	if (mesh->MRABNelements[l]) {
	  mesh->updateKernel(mesh->MRABNelements[l],
			     mesh->o_MRABelementIds[l],
			     mesh->MRSAAB_C[l],
			     mesh->MRAB_A[id+0],
			     mesh->MRAB_A[id+1],
			     mesh->MRAB_A[id+2],
			     mesh->MRAB_A[id+3],
			     mesh->MRSAAB_A[id+0],
			     mesh->MRSAAB_A[id+1],
			     mesh->MRSAAB_A[id+2],
			     mesh->MRSAAB_A[id+3],
			     mesh->MRABshiftIndex[l],
			     mesh->o_qFiltered,
			     //mesh->o_rhsq,
			     mesh->o_fQM,
			     mesh->o_qCorr,
			     mesh->o_q);
	
	  //we *must* use 2 here (n - 1), so rk coefficients point the right direction in time
	  mesh->MRABshiftIndex[l] = (mesh->MRABshiftIndex[l]+mesh->Nrhs-1)%mesh->Nrhs;
	}
      }
      
      occa::toc("updateKernel");
      
      if (levS<mesh->MRABNlevels) {
	const iint id = mrab_order*mesh->MRABNlevels*mesh->Nrhs + levS*mesh->Nrhs;
	
	if (mesh->MRABNhaloElements[levS]) {
	  //trace update using same kernel
	  mesh->traceUpdateKernel(mesh->MRABNhaloElements[levS],
				  mesh->o_MRABhaloIds[levS],
				  mesh->MRSAAB_C[levS-1], //
				  mesh->MRAB_B[id+0], //
				  mesh->MRAB_B[id+1],
				  mesh->MRAB_B[id+2],
				  mesh->MRAB_B[id+3],//
				  mesh->MRSAAB_B[id+0], //
				  mesh->MRSAAB_B[id+1],
				  mesh->MRSAAB_B[id+2],
				  mesh->MRSAAB_B[id+3],
				  mesh->MRABshiftIndex[levS],
				  mesh->o_qFiltered,
				  //mesh->o_rhsq,
				  mesh->o_fQM,
				  mesh->o_qPreFilter,
				  mesh->o_qCorr,
				  mesh->o_q);
      	}
      }

      for (iint l = 0; l < levS; ++l) {
	if (mesh->MRABNelements[l]) {
	  mesh->filterKernelLevelsH(mesh->MRABNelements[l],
				mesh->o_MRABelementIds[l],
				mesh->o_dualProjMatrix,
				mesh->o_cubeFaceNumber,
				mesh->o_EToE,
				mesh->o_fQM,
				mesh->o_qPreFilter);
	}
      }
	
      for (iint l = 0; l < levS; ++l) {
	if (mesh->MRABNelements[l]) {
	  mesh->filterKernelLevelsV(mesh->MRABNelements[l],
				    mesh->o_MRABelementIds[l],
				    alpha,
				    mesh->o_dualProjMatrix,
				    mesh->o_cubeFaceNumber,
				    mesh->o_EToE,
				    mesh->o_x,
				    mesh->o_y,
				    mesh->o_z,
				    mesh->o_qPreFilter,
				    mesh->o_fQM,
				    mesh->o_q);
	}
      }
    }

    /*    if (mesh->NtimeSteps - (tstep + 1) < 20) {
      mesh->o_q.copyTo(mesh->q);
      dfloat t = mesh->dt*(tstep+1)*pow(2,mesh->MRABNlevels-1);
      advectionErrorNormQuad3D(mesh,t,NULL,0);
      }*/
      
    
    // estimate maximum error
    /*    if((((tstep+1)%mesh->errorStep)==0)){
      //	dfloat t = (tstep+1)*mesh->dt;
      dfloat t = mesh->dt*((tstep+1)*pow(2,mesh->MRABNlevels-1));
	
      printf("tstep = %d, t = %g\n", tstep, t);
      fflush(stdout);
      // copy data back to host
      mesh->o_q.copyTo(mesh->q);

      // check for nans
      for(int n=0;n<mesh->Nfields*mesh->Nelements*mesh->Np;++n){
	if(isnan(mesh->q[n])){
	  printf("found nan\n");
	  exit(-1);
	}
      }

      advectionPlotNorms(mesh,"norms",tstep/mesh->errorStep,mesh->q);

      // output field files
      iint fld = 0;
      char fname[BUFSIZ];

      //advectionPlotVTUQuad3DV2(mesh, "foo", tstep/mesh->errorStep);
      } */       
    occa::printTimer();
  }
  
  free(recvBuffer);
  free(sendBuffer);
}


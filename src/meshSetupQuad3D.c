#include "mesh3D.h"

mesh_t *meshSetupQuad3D(int mesh_size, int N, dfloat sphereRadius,char *mode){

  // read chunk of elements
  char filename[40];
  sprintf(filename,"../../meshes/cubed_grid_%d.msh",mesh_size);
  //sprintf(filename,"./meshing/test_mesh2.msh");
  mesh_t *mesh = meshParallelReaderQuad3D(filename);
  mesh->edgeLength = mesh_size;

  // set sphere radius (will be used later in building physical nodes)
  mesh->sphereRadius = sphereRadius;

  // partition elements using Morton ordering & parallel sort
  //meshGeometricPartition3D(mesh); // need to double check this

  // connect elements using parallel sort
  meshParallelConnect(mesh);
  
  // print out connectivity statistics
  meshPartitionStatistics(mesh);

  // connect elements to boundary faces
  meshConnectBoundary(mesh);

#if 0
  for(int e=0;e<mesh->Nelements;++e){
    for(int f=0;f<mesh->Nfaces;++f){
      printf("%d ", mesh->EToB[e*mesh->Nfaces+f]);
    }
    printf("\n");
  }
#endif
  
  // load reference (r,s) element nodes
  void meshLoadReferenceNodesQuad3D(mesh_t *mesh, int N);
  meshLoadReferenceNodesQuad3D(mesh, N);
  
  // compute physical (x,y,z) locations of the element nodes
  if (strstr(mode,"flat")) {
    meshPreserveGridQuad3D(mesh);
    meshPhysicalNodesQuad3D(mesh);
  }
  else if (strstr(mode,"sphere")) {
    meshPreserveGridQuad3D(mesh);
    meshSphericalNodesQuad3D(mesh);
  }
  else if (strstr(mode,"equispherical")) {
    meshPreserveGridQuad3D(mesh);
    meshEquiSphericalNodesQuad3D(mesh);
  }
  else if (strstr(mode,"extended")) {
    meshExtendGridQuad3D(mesh);
    meshEquiSphericalNodesQuad3D(mesh);
    meshEquiSphericalExtensionQuad3D(mesh);
  }
  else {
    printf("ERROR: Grid mode not recognized");
  }
  
  // set up halo exchange info for MPI (do before connect face nodes)
  meshHaloSetup(mesh);

  // compute geometric factors
  meshGeometricFactorsQuad3D(mesh);
  
  // connect face nodes (find trace indices)
  meshConnectFaceNodes3D(mesh);

  for(int n=0;n<mesh->Nfp*mesh->Nelements*mesh->Nfaces;++n){
    if(mesh->vmapM[n]==mesh->vmapP[n]){
      printf("node %d matches self \n");
    }
  }
      
  
  // compute surface geofacs
  meshSurfaceGeometricFactorsQuad3D(mesh);
  
  // global nodes
  meshParallelConnectNodes(mesh);
     
  return mesh;
}

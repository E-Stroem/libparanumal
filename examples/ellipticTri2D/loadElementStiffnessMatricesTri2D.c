#include "ellipticTri2D.h"
void loadElementStiffnessMatricesTri2D(mesh2D *mesh, const char *options, int N){

  char fname[BUFSIZ];
  sprintf(fname, "sparseN%02d.dat", N);
  FILE *fp = fopen(fname, "r");
  printf("opened\n");

  char buf[BUFSIZ];
  printf("buffer created\n");
  if (fp == NULL) {
    printf("no file! %s\n", fname);
    exit(-1);
  }

  fgets(buf, BUFSIZ, fp); // read comment
  printf("Comment 1%s ", buf);

  fgets(buf, BUFSIZ, fp);
  int maxNnzPerRow;
  sscanf(buf, "%d", &maxNnzPerRow);
  printf("maxNNz = %d Np = %d \n", maxNnzPerRow, mesh->Np);

  mesh->maxNnzPerRow = maxNnzPerRow;
  fgets(buf, BUFSIZ, fp); // another  comment
  printf("Comment 2%s ", buf);
  mesh->Ind = (iint*) calloc(mesh->maxNnzPerRow*mesh->Np, sizeof(iint));                     
  for(int n=0;n<mesh->maxNnzPerRow*mesh->Np;++n){                                           
    fscanf(fp, "%d ", mesh->Ind+n);                                                
    //fscanf(fp, " %s ", buf);    
    printf(" %d ", mesh->Ind[n]);
    //printf("buf %s ", buf); 
    if ((n+1)%mesh->Np == 0){printf(" \n ");}
  }   
  mesh->Srr =  (dfloat*) calloc(mesh->maxNnzPerRow*mesh->Np, sizeof(dfloat));
  mesh->Srs =  (dfloat*) calloc(mesh->maxNnzPerRow*mesh->Np, sizeof(dfloat));
  mesh->Sss =  (dfloat*) calloc(mesh->maxNnzPerRow*mesh->Np, sizeof(dfloat));
  fgets(buf, BUFSIZ, fp);
  //fscanf(fp, "%s", buf);
  printf("Comment 3 %s ", buf);
  printf("Srr :\n");

  for(int n=0;n<mesh->maxNnzPerRow*mesh->Np;++n){                                           
    char test[1000];    
    int aa = fscanf(fp,  "%lf ", mesh->Srr+n);                                                
    printf(" %f ", mesh->Srr[n]);
    //mesh->Srr[n]); 
    if ((n+1)%mesh->Np == 0){
      printf(" \n ");
    }

  }   
  printf("\nSrs \n");


  fgets(buf, BUFSIZ, fp);

  for(int n=0;n<mesh->maxNnzPerRow*mesh->Np;++n){                                           
    fscanf(fp, "%lf ", mesh->Srs+n);
    printf(" %f ", mesh->Srs[n]);
    //mesh->Srr[n]); 
    if ((n+1)%mesh->Np == 0){
      printf(" \n ");
    }

  } 
  fgets(buf, BUFSIZ, fp);
  printf("\n Sss \n");

  for(int n=0;n<mesh->maxNnzPerRow*mesh->Np;++n){                                           
    fscanf(fp, "%lf ", mesh->Sss+n);                                                
    printf(" %f ", mesh->Sss[n]);
    //mesh->Srr[n]); 
    if ((n+1)%mesh->Np == 0){
      printf(" \n ");
    }

  }   
  printf("\n");

  /*char4 packing*/
  int paddedRowSize = mesh->maxNnzPerRow+ (4-(mesh->maxNnzPerRow%4));

  //now occa copy, transpose and stuff
  dfloat *SrrT, *SrsT, *SsrT, *SssT;
  iint *IndT;  
  SrrT = (dfloat *) calloc(paddedRowSize*mesh->Np,sizeof(dfloat));
  SrsT = (dfloat *) calloc(paddedRowSize*mesh->Np,sizeof(dfloat));
  SssT = (dfloat *) calloc(paddedRowSize*mesh->Np,sizeof(dfloat));
  IndT = (iint*)   calloc(paddedRowSize*mesh->Np,sizeof(iint));


  printf("\nIND transose\n");
  for (iint n=0;n<mesh->Np;n++) {
    for (iint m=0;m<mesh->maxNnzPerRow;m++) {  
      /*SrrT[m+n*mesh->maxNnzPerRow] = mesh->Srr[n+m*mesh->Np];
        SrsT[m+n*mesh->maxNnzPerRow] = mesh->Srs[n+m*mesh->Np];
        SssT[m+n*mesh->maxNnzPerRow] = mesh->Sss[n+m*mesh->Np];
        IndT[m+n*mesh->maxNnzPerRow] = mesh->Ind[n+m*mesh->Np];
        */
      SrrT[m+n*paddedRowSize] = mesh->Srr[n+m*mesh->Np];
      SrsT[m+n*paddedRowSize] = mesh->Srs[n+m*mesh->Np];
      SssT[m+n*paddedRowSize] = mesh->Sss[n+m*mesh->Np];
      IndT[m+n*paddedRowSize] = mesh->Ind[n+m*mesh->Np];

      //printf("IndT[%d] =  %d \n",m+n*paddedRowSize,  IndT[m+n*paddedRowSize]);   
    }
    //printf("\n");
  }
  char * IndTchar = (char*) calloc(paddedRowSize*mesh->Np,sizeof(char));
  for (iint n=0;n<mesh->Np;n++) {
    for (iint m=0;m<paddedRowSize;m++) {  
      IndTchar[m+n*paddedRowSize] = (char) IndT[m+n*paddedRowSize];
      printf("IndT[%d] =  %d \n",m+n*paddedRowSize,  IndT[m+n*paddedRowSize]);   
    }
    printf("\n");
  }
printf("\n\nSrrT: ");
  for (iint n=0;n<mesh->Np;n++) {
    for (iint m=0;m<paddedRowSize;m++) {  
      printf("SrsT[%d] =  %f \n",m+n*paddedRowSize,  SrrT[m+n*paddedRowSize]);   
    }
    printf("\n");
  }



  //ACHTUNG!!! ACHTUNG!!! ACHTUNG!!! mesh->maxNnzPerRow changes to acchount for padding!!!
  mesh->maxNnzPerRow = paddedRowSize;
  mesh->o_SrrT = mesh->device.malloc(mesh->Np*mesh->maxNnzPerRow*sizeof(dfloat), SrrT);
  mesh->o_SrsT = mesh->device.malloc(mesh->Np*mesh->maxNnzPerRow*sizeof(dfloat), SrsT);
  mesh->o_SssT = mesh->device.malloc(mesh->Np*mesh->maxNnzPerRow*sizeof(dfloat), SssT);
  mesh->o_IndT = mesh->device.malloc(mesh->Np*mesh->maxNnzPerRow*sizeof(iint), IndT);
mesh->o_IndTchar = mesh->device.malloc(mesh->Np*mesh->maxNnzPerRow*sizeof(char), IndTchar);
}

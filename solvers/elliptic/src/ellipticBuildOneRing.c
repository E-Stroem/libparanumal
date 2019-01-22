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

#include "elliptic.h"

typedef struct{

  hlong vertex;
  hlong element;
  hlong sourceRank;
  hlong sortTag;

}vertex_t;


int compareSortTag(const void *a, 
		    const void *b){

  vertex_t *va = (vertex_t*) a;
  vertex_t *vb = (vertex_t*) b;

  if(va->sortTag < vb->sortTag) return -1;
  if(va->sortTag > vb->sortTag) return +1;

  return 0;
}

int compareSourceRankElement(const void *a, 
			    const void *b){
  
  vertex_t *va = (vertex_t*) a;
  vertex_t *vb = (vertex_t*) b;

  if(va->sourceRank < vb->sourceRank) return -1;
  if(va->sourceRank > vb->sourceRank) return +1;

  if(va->element < vb->element) return -1;
  if(va->element > vb->element) return +1;

  
  return 0;
}

// build one ring including MPI exchange information

void ellipticBuildOneRing(elliptic_t *elliptic){

  mesh_t *mesh = elliptic->mesh;

  vertex_t *vertexSendList = (vertex_t*) calloc(mesh->Nelements*mesh->Nverts, sizeof(vertex_t));

  hlong *vertexSendCounts = (hlong*) calloc(mesh->size, sizeof(hlong));
  hlong *vertexRecvCounts = (hlong*) calloc(mesh->size, sizeof(hlong));

  hlong cnt = 0;
  for(hlong e=0;e<mesh->Nelements;++e){
    for(int v=0;v<mesh->Nverts;++v){
      vertexSendList[cnt].vertex = mesh->EToV[e*mesh->Nverts+v];
      vertexSendList[cnt].element = e;
      vertexSendList[cnt].sourceRank = mesh->rank;
      vertexSendList[cnt].sortTag = vertexSendList[cnt].vertex%mesh->size;
      ++vertexSendCounts[vertexSendList[cnt].sortTag];
      ++cnt;
    }
  }
  
  // sort based on sortTag (=vertex%size)
  qsort(vertexSendList, cnt, sizeof(vertex_t), compareSortTag);

  // send sortTagCounts (hackety)
  MPI_Alltoall(vertexSendCounts, 1, MPI_HLONG,
	       vertexRecvCounts, 1, MPI_HLONG,
	       mesh->comm);

  // exchange vertices
  hlong *vertexSendDispls = (hlong*) calloc(mesh->size+1, sizeof(hlong));
  hlong *vertexRecvDispls = (hlong*) calloc(mesh->size+1, sizeof(hlong));
  hlong NvertexSend = 0;
  hlong NvertexRecv = 0;
  for(int r=0;r<mesh->size;++r){

    NvertexSend += vertexSendCounts[r];
    NvertexRecv += vertexRecvCounts[r];
    
    vertexSendCounts[r] *= sizeof(vertex_t); // hack-hack-hack
    vertexRecvCounts[r] *= sizeof(vertex_t); // hack-hack-hack
    
    vertexSendDispls[r+1] = vertexSendDispls[r] + vertexSendCounts[r];
    vertexRecvDispls[r+1] = vertexRecvDispls[r] + vertexRecvCounts[r];

#if 0
    printf("rank %d: send %d, %d  recv %d, %d\n",
	   mesh->rank,
	   vertexSendCounts[r], vertexSendDispls[r],
	   vertexRecvCounts[r], vertexRecvDispls[r]);
#endif
  }

  // hack-hack-hack
  vertex_t *vertexRecvList = (vertex_t*) calloc(NvertexRecv, sizeof(vertex_t)); 
  
  MPI_Alltoallv(vertexSendList, vertexSendCounts, vertexSendDispls, MPI_CHAR,
		vertexRecvList, vertexRecvCounts, vertexRecvDispls, MPI_CHAR,
		mesh->comm);

  for(int v=0;v<NvertexRecv;++v){
    vertexRecvList[v].sortTag = vertexRecvList[v].vertex;
  }
  
  // sort received vertex based on sortTag (=vertex number)
  qsort(vertexRecvList, NvertexRecv, sizeof(vertex_t), compareSortTag);  

  // count number of unique received vertices
  hlong NvertexUniqueRecv = (NvertexRecv>0) ? 1:0;
  for(hlong n=1;n<NvertexRecv;++n){
    if(compareSortTag(vertexRecvList+n, vertexRecvList+n-1)!=0) { // new vertex
      ++NvertexUniqueRecv;
    }
  }

  // find offset of the start of each new unique vertex  in sorted list
  hlong *vertexUniqueRecvOffsets = (hlong*) calloc(NvertexUniqueRecv+1, sizeof(hlong));
  
  cnt = 1;
  vertexUniqueRecvOffsets[0] = 0;
  for(hlong n=1;n<NvertexRecv;++n){
    if(compareSortTag(vertexRecvList+n, vertexRecvList+n-1)!=0) { // new vertex
      vertexUniqueRecvOffsets[cnt] = n;
      ++cnt;
    }
  }
  vertexUniqueRecvOffsets[cnt] = NvertexRecv; // cap at end
  
  // now count how many vertices to send to each rank
  hlong *vertexOneRingSendCounts = (hlong*) calloc(mesh->size, sizeof(hlong));
  hlong Ntotal = 0;
  for(hlong n=0;n<NvertexUniqueRecv;++n){
    hlong start = vertexUniqueRecvOffsets[n];
    hlong end   = vertexUniqueRecvOffsets[n+1];

    int NuniqueRecvMultiplicity = end - start;
    for(hlong m=start;m<end;++m){
      vertexOneRingSendCounts[vertexRecvList[m].sourceRank] += NuniqueRecvMultiplicity; // watch out for this
      Ntotal += NuniqueRecvMultiplicity;
    }
  }

  vertex_t *vertexOneRingSendList = (vertex_t*) calloc(Ntotal, sizeof(vertex_t));
  cnt = 0;
  for(hlong n=0;n<NvertexUniqueRecv;++n){
    hlong start = vertexUniqueRecvOffsets[n];    
    hlong end   = vertexUniqueRecvOffsets[n+1];
    int NuniqueRecvMultiplicity = end - start;

    for(hlong v1=start;v1<end;++v1){ // vertex v1 to be sent back with list of conns
      hlong dest = vertexRecvList[v1].sourceRank;
      for(hlong v2=start;v2<end;++v2){
	vertexOneRingSendList[cnt] = vertexRecvList[v2];
	vertexOneRingSendList[cnt].sortTag = dest;
	++cnt;
      }
    }
  }

  hlong NvertexOneRingSend = cnt;

  // sort OneRing send list based on sort rank (=destination tag)
  qsort(vertexOneRingSendList, NvertexOneRingSend, sizeof(vertex_t), compareSortTag);   // check qsort counts

  // now figure out how many oneRing vertices to expect
  hlong *vertexOneRingRecvCounts = (hlong*) calloc(mesh->size, sizeof(hlong));
  MPI_Alltoall(vertexOneRingSendCounts, 1, MPI_HLONG,
	       vertexOneRingRecvCounts, 1, MPI_HLONG,
	       mesh->comm);

  // find displacements for
  hlong *vertexOneRingSendDispls = (hlong*) calloc(mesh->size+1, sizeof(hlong));
  hlong *vertexOneRingRecvDispls = (hlong*) calloc(mesh->size+1, sizeof(hlong));
  hlong NvertexOneRingRecv = 0;

  for(int r=0;r<mesh->size;++r){
    NvertexOneRingRecv += vertexOneRingRecvCounts[r];
    vertexOneRingSendCounts[r] *= sizeof(vertex_t);
    vertexOneRingRecvCounts[r] *= sizeof(vertex_t);
    vertexOneRingSendDispls[r+1] = vertexOneRingSendDispls[r] + vertexOneRingSendCounts[r];
    vertexOneRingRecvDispls[r+1] = vertexOneRingRecvDispls[r] + vertexOneRingRecvCounts[r];
  }
  
  vertex_t *vertexOneRingRecvList = (vertex_t*) calloc(NvertexOneRingRecv, sizeof(vertex_t)); // hack-hack-hack

  // sned element lists to the relevant ranks
  MPI_Alltoallv(vertexOneRingSendList, vertexOneRingSendCounts, vertexOneRingSendDispls, MPI_CHAR,
		vertexOneRingRecvList, vertexOneRingRecvCounts, vertexOneRingRecvDispls, MPI_CHAR,
		mesh->comm);

  // finally we now have a list of all elements that are needed to form the 1-ring (to rule them all)

  // sort the list by "source rank then element"
  qsort(vertexOneRingRecvList, NvertexOneRingRecv, sizeof(vertex_t), compareSourceRankElement); 

  // remove local elements from oneRing list
  cnt = 0;
  for(hlong v=0;v<NvertexOneRingRecv;++v){
    if(vertexOneRingRecvList[v].sourceRank != mesh->rank){ // rule out local elements
      vertexOneRingRecvList[cnt] = vertexOneRingRecvList[v];
      ++cnt;
    }
  }
  NvertexOneRingRecv = cnt;

  // remove duplicate elements from oneRing list
  cnt = 1; // assumes at least one oneRing element
  for(hlong v=1;v<NvertexOneRingRecv;++v){
    if(! (vertexOneRingRecvList[v].element == vertexOneRingRecvList[cnt-1].element
	  && vertexOneRingRecvList[v].sourceRank == vertexOneRingRecvList[cnt-1].sourceRank)){
      vertexOneRingRecvList[cnt] = vertexOneRingRecvList[v];
      ++cnt;
    }
  }

  NvertexOneRingRecv = cnt;
  
  hlong NnonLocalOneRingElements = cnt;
  vertex_t *nonLocalOneRingElements = (vertex_t*) calloc(NnonLocalOneRingElements, sizeof(vertex_t));

  // next
  // 1. populate this list
  // 2. set up a meshOneRing that has an attached oneRing for the one ring
  // 3. set up the gs info [ need to understand how to populate from the local elements on each rank to the oneRing ]
  
#if 1
  for(hlong v=0;v<NnonLocalOneRingElements;++v){
    printf("%d %d %d ([rank] receives [element] from [source rank])\n",
	   mesh->rank, vertexOneRingRecvList[v].element, vertexOneRingRecvList[v].sourceRank);
  }
#endif

  free(vertexSendList);
  free(vertexSendCounts);
  free(vertexRecvCounts);
  free(vertexSendDispls);
  free(vertexRecvDispls);
  free(vertexRecvList);   
  free(vertexUniqueRecvOffsets);
  free(vertexOneRingSendCounts);    
  free(vertexOneRingSendList);   
  free(vertexOneRingRecvCounts);    
  free(vertexOneRingSendDispls);   
  free(vertexOneRingRecvDispls);    
  free(vertexOneRingRecvList);
  
}

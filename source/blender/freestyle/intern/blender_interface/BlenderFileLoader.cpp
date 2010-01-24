#include "BlenderFileLoader.h"

#include <assert.h>

BlenderFileLoader::BlenderFileLoader(Render *re, SceneRenderLayer* srl)
{
	_re = re;
	_srl = srl;
	_Scene = NULL;
	_numFacesRead = 0;
	_minEdgeSize = DBL_MAX;
}

BlenderFileLoader::~BlenderFileLoader()
{
	_Scene = NULL;
}

NodeGroup* BlenderFileLoader::Load()
{
	ObjectInstanceRen *obi;
	ObjectRen *obr;

	cout << "\n===  Importing triangular meshes into Blender  ===" << endl;

	// creation of the scene root node
	_Scene = new NodeGroup;

	_viewplane_left=   _re->viewplane.xmin;
	_viewplane_right=  _re->viewplane.xmax;
	_viewplane_bottom= _re->viewplane.ymin;
	_viewplane_top=    _re->viewplane.ymax;
	_z_near= -_re->clipsta;
	_z_far=  -_re->clipend;
#if 0
	cout << "frustrum: l " << _viewplane_left << " r " << _viewplane_right
		<< " b " << _viewplane_bottom << " t " << _viewplane_top
		<< " n " << _z_near << " f " << _z_far << endl;
#endif

	int id = 0;
	for(obi= (ObjectInstanceRen *) _re->instancetable.first; obi; obi=obi->next) {
		if (!(obi->lay & _re->scene->lay & _srl->lay))
			continue;

		obr= obi->obr;
		
		if( obr->totvlak > 0)
			insertShapeNode(obr, ++id);
		else
			cout << "  Sorry, only vlak-based shapes are supported." << endl;
	}

	//Returns the built scene.
	return _Scene;
}

#define CLIPPED_BY_NEAR -1
#define NOT_CLIPPED      0
#define CLIPPED_BY_FAR   1

// check if each vertex of a triangle (V1, V2, V3) is clipped by the near/far plane
// and calculate the number of triangles to be generated by clipping
int BlenderFileLoader::countClippedFaces(VertRen *v1, VertRen *v2, VertRen *v3, int clip[3])
{
	VertRen *v[3];
	int numClipped, sum, numTris;

	v[0] = v1;
	v[1] = v2;
	v[2] = v3;
	numClipped = sum = 0;
	for (int i = 0; i < 3; i++) {
		if (v[i]->co[2] > _z_near) {
			clip[i] = CLIPPED_BY_NEAR;
			numClipped++;
		} else if (v[i]->co[2] < _z_far) {
			clip[i] = CLIPPED_BY_FAR;
			numClipped++;
		} else {
			clip[i] = NOT_CLIPPED;
		}
//		printf("%d %s\n", i, (clip[i] == NOT_CLIPPED) ? "not" : (clip[i] == CLIPPED_BY_NEAR) ? "near" : "far");
		sum += clip[i];
	}
	switch (numClipped) {
	case 0:
		numTris = 1; // triangle
		break;
	case 1:
		numTris = 2; // tetragon
		break;
	case 2:
		if (sum == 0)
			numTris = 3; // pentagon
		else
			numTris = 1; // triangle
		break;
	case 3:
		if (sum == 3 || sum == -3)
			numTris = 0;
		else
			numTris = 2; // tetragon
		break;
	}
	return numTris;
}

// find the intersection point C between the line segment from V1 to V2 and
// a clipping plane at depth Z (i.e., the Z component of C is known, while
// the X and Y components are unknown).
void BlenderFileLoader::clipLine(VertRen *v1, VertRen *v2, float c[3], float z)
{
	double d[3];
	for (int i = 0; i < 3; i++)
		d[i] = v2->co[i] - v1->co[i];
	double t = (z - v1->co[2]) / d[2];
	c[0] = v1->co[0] + t * d[0];
	c[1] = v1->co[1] + t * d[1];
	c[2] = z;
}

// clip the triangle (V1, V2, V3) by the near and far clipping plane and
// obtain a set of vertices after the clipping.  The number of vertices
// is at most 5.
void BlenderFileLoader::clipTriangle(int numTris, float triCoords[][3], VertRen *v1, VertRen *v2, VertRen *v3, int clip[3])
{
	VertRen *v[3];
	int i, j, k;

	v[0] = v1;
	v[1] = v2;
	v[2] = v3;
	k = 0;
	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		if (clip[i] == NOT_CLIPPED) {
			copy_v3_v3(triCoords[k++], v[i]->co);
			if (clip[j] != NOT_CLIPPED) {
				clipLine(v[i], v[j], triCoords[k++], (clip[j] == CLIPPED_BY_NEAR) ? _z_near : _z_far);
			}
		} else if (clip[i] != clip[j]) {
			if (clip[j] == NOT_CLIPPED) {
				clipLine(v[i], v[j], triCoords[k++], (clip[i] == CLIPPED_BY_NEAR) ? _z_near : _z_far);
			} else {
				clipLine(v[i], v[j], triCoords[k++], (clip[i] == CLIPPED_BY_NEAR) ? _z_near : _z_far);
				clipLine(v[i], v[j], triCoords[k++], (clip[j] == CLIPPED_BY_NEAR) ? _z_near : _z_far);
			}
		}
	}
	assert (k == 2 + numTris);
}

void BlenderFileLoader::addTriangle(struct LoaderState *ls, float v1[3], float v2[3], float v3[3])
{
	float v12[3], v13[3], n[3];
	float *fv[3], len;
	unsigned i, j;

	// initialize the bounding box by the first vertex
	if (ls->currentIndex == 0) {
		copy_v3_v3(ls->minBBox, v1);
		copy_v3_v3(ls->maxBBox, v1);
	}

	// compute the normal of the triangle
	sub_v3_v3v3(v12, v1, v2);
	sub_v3_v3v3(v13, v1, v3);
	cross_v3_v3v3(n, v12, v13);
	normalize_v3(n);

	fv[0] = v1;
	fv[1] = v2;
	fv[2] = v3;
	for (i = 0; i < 3; i++) {

		copy_v3_v3(ls->pv, fv[i]);
		copy_v3_v3(ls->pn, n);

		// update the bounding box
		for (j = 0; j < 3; j++)
		{
	  		if (ls->minBBox[j] > ls->pv[j])
	    		ls->minBBox[j] = ls->pv[j];

	  		if (ls->maxBBox[j] < ls->pv[j])
	    		ls->maxBBox[j] = ls->pv[j];
		}

		len = len_v3v3(fv[i], fv[(i + 1) % 3]);
		if (_minEdgeSize > len)
	  		_minEdgeSize = len;
		
		*ls->pvi = ls->currentIndex;
		*ls->pni = ls->currentIndex;
		*ls->pmi = ls->currentMIndex;

		ls->currentIndex +=3;
		ls->pv += 3;
		ls->pn += 3;

		ls->pvi++;
		ls->pni++;
		ls->pmi++;
	}
}

void BlenderFileLoader::insertShapeNode(ObjectRen *obr, int id)
{
	VlakRen *vlr;
	
	// Mesh *mesh = (Mesh *)ob->data;
	//---------------------
	// mesh => obr
	
	// We invert the matrix in order to be able to retrieve the shape's coordinates in its local coordinates system (origin is the iNode pivot)
	// Lib3dsMatrix M;
	// lib3ds_matrix_copy(M, mesh->matrix);
	// lib3ds_matrix_inv(M);
	//---------------------
	// M allows to recover world coordinates from camera coordinates
	// M => obr->ob->imat * obr->obmat  (multiplication from left to right)
	float M[4][4];
	mul_m4_m4m4(M, obr->ob->imat, obr->ob->obmat); 
	
	// We compute a normal per vertex and manages the smoothing of the shape:
	// Lib3dsVector *normalL=(Lib3dsVector*)malloc(3*sizeof(Lib3dsVector)*mesh->faces);
	// lib3ds_mesh_calculate_normals(mesh, normalL);
	// mesh_calc_normals(mesh->mvert, mesh->totvert, mesh->mface, mesh->totface, NULL);
	//---------------------
	// already calculated and availabe in vlak ?	
//	printf("%s\n", obr->ob->id.name + 2);
	
	// We build the rep:
	IndexedFaceSet *rep;
	unsigned numFaces = 0;
	int clip_1[3], clip_2[3];
	for(int a=0; a < obr->totvlak; a++) {
		if((a & 255)==0) vlr= obr->vlaknodes[a>>8].vlak;
		else vlr++;
//		printf("v1 %f, %f, %f\n", vlr->v1->co[0], vlr->v1->co[1], vlr->v1->co[2]);
//		printf("v2 %f, %f, %f\n", vlr->v2->co[0], vlr->v2->co[1], vlr->v2->co[2]);
//		printf("v3 %f, %f, %f\n", vlr->v3->co[0], vlr->v3->co[1], vlr->v3->co[2]);
//		if (vlr->v4) printf("v4 %f, %f, %f\n", vlr->v4->co[0], vlr->v4->co[1], vlr->v4->co[2]);
		numFaces += countClippedFaces(vlr->v1, vlr->v2, vlr->v3, clip_1);
		if (vlr->v4)
			numFaces += countClippedFaces(vlr->v1, vlr->v3, vlr->v4, clip_2);
	}
//	cout <<"numFaces " <<numFaces<<endl;
	if (numFaces == 0)
		return;

	NodeTransform *currentMesh = new NodeTransform;
	NodeShape * shape = new NodeShape;

	unsigned vSize = 3*3*numFaces;
	float *vertices = new float[vSize];
	unsigned nSize = vSize;
	float *normals = new float[nSize];
	unsigned *numVertexPerFaces = new unsigned[numFaces];
	vector<FrsMaterial> meshFrsMaterials;
	
	IndexedFaceSet::TRIANGLES_STYLE *faceStyle = new IndexedFaceSet::TRIANGLES_STYLE[numFaces];
	unsigned i;
	for (i = 0; i <numFaces; i++) {
	  faceStyle[i] = IndexedFaceSet::TRIANGLES;
	  numVertexPerFaces[i] = 3;
	}
	
	unsigned viSize = 3*numFaces;
	unsigned *VIndices = new unsigned[viSize];
	unsigned niSize = viSize;
	unsigned *NIndices = new unsigned[niSize];
	unsigned *MIndices = new unsigned[viSize]; // Material Indices
	
	struct LoaderState ls;
	ls.pv = vertices;
	ls.pn = normals;
	ls.pvi = VIndices;
	ls.pni = NIndices;
	ls.pmi = MIndices;
	ls.currentIndex = 0;
	ls.currentMIndex = 0;
	
	FrsMaterial tmpMat;
	
	// we want to find the min and max coordinates as we build the rep. 
	// We initialize the min and max values whith the first vertex.
	//lib3ds_vector_transform(pvtmp, M, mesh->pointL[mesh->faceL[0].points[0]].pos);

	int p;
	for(p=0; p < obr->totvlak; ++p) // we parse the faces of the mesh
	{
			// Lib3dsFace *f=&mesh->faceL[p];
			// Lib3dsMaterial *mat=0;
			if((p & 255)==0) vlr = obr->vlaknodes[p>>8].vlak;
			else vlr++;

			unsigned numTris_1, numTris_2;
			numTris_1 = countClippedFaces(vlr->v1, vlr->v2, vlr->v3, clip_1);
			numTris_2 = (vlr->v4) ? countClippedFaces(vlr->v1, vlr->v3, vlr->v4, clip_2) : 0;
			if (numTris_1 == 0 && numTris_2 == 0)
				continue;

			Material *mat = vlr->mat;
	
			if (mat) 
			{
			    tmpMat.setDiffuse( mat->r, mat->g, mat->b, mat->alpha );
			    tmpMat.setSpecular( mat->specr, mat->specg, mat->specb, mat->spectra);
			    float s = 1.0 * (mat->har + 1) / 4 ; // in Blender: [1;511] => in OpenGL: [0;128]
			    if(s > 128.f)
			      s = 128.f;
			    tmpMat.setShininess(s);
			}
	  
			if(meshFrsMaterials.empty())
			{
				meshFrsMaterials.push_back(tmpMat);
		    	shape->setFrsMaterial(tmpMat);
		  	} else {
		    	// find if the material is aleady in the list
		    	unsigned i=0;
		    	bool found = false;
		
		    	for(vector<FrsMaterial>::iterator it=meshFrsMaterials.begin(), itend=meshFrsMaterials.end();
		    		it!=itend;
		    		++it){
		      			if(*it == tmpMat){
		        			ls.currentMIndex = i;
		        			found = true;
		        			break;
		      			}
		      			++i;
		    	}
		
		    	if(!found){
		      		meshFrsMaterials.push_back(tmpMat);
		      		ls.currentMIndex = meshFrsMaterials.size()-1;
		    	}
	  		}

			float triCoords[5][3];

			if (numTris_1 > 0) {
				clipTriangle(numTris_1, triCoords, vlr->v1, vlr->v2, vlr->v3, clip_1);
				for (i = 0; i < 2 + numTris_1; i++) {
					mul_m4_v3(M, triCoords[i]); // camera to world
//					printf("%d %f, %f, %f\n", i, triCoords[i][0], triCoords[i][1], triCoords[i][2]);
				}
				for (i = 0; i < numTris_1; i++) {
					addTriangle(&ls, triCoords[0], triCoords[i+1], triCoords[i+2]);
					_numFacesRead++;
				}
			}

			if (numTris_2 > 0) {
				clipTriangle(numTris_2, triCoords, vlr->v1, vlr->v3, vlr->v4, clip_2);
				for (i = 0; i < 2 + numTris_2; i++) {
					mul_m4_v3(M, triCoords[i]); // camera to world
//					printf("%d %f, %f, %f\n", i, triCoords[i][0], triCoords[i][1], triCoords[i][2]);
				}
				for (i = 0; i < numTris_2; i++) {
					addTriangle(&ls, triCoords[0], triCoords[i+1], triCoords[i+2]);
					_numFacesRead++;
				}
			}
	}
	
	// We might have several times the same vertex. We want a clean 
	// shape with no real-vertex. Here, we are making a cleaning 
	// pass.
	real *cleanVertices = NULL;
	unsigned   cvSize;
	unsigned   *cleanVIndices = NULL;
	
	GeomCleaner::CleanIndexedVertexArray(
	  vertices, vSize, 
	  VIndices, viSize,
	  &cleanVertices, &cvSize, 
	  &cleanVIndices);
	
	real *cleanNormals = NULL;
	unsigned   cnSize;
	unsigned   *cleanNIndices = NULL;
	
	GeomCleaner::CleanIndexedVertexArray(
	  normals, nSize, 
	  NIndices, niSize,
	  &cleanNormals, &cnSize, 
	  &cleanNIndices);
	
	// format materials array
	FrsMaterial** marray = new FrsMaterial*[meshFrsMaterials.size()];
	unsigned mindex=0;
	for(vector<FrsMaterial>::iterator m=meshFrsMaterials.begin(), mend=meshFrsMaterials.end();
	    m!=mend;
	    ++m){
	  marray[mindex] = new FrsMaterial(*m);
	  ++mindex;
	}
	// deallocates memory:
	delete [] vertices;
	delete [] normals;
	delete [] VIndices;
	delete [] NIndices;
	
	// Create the IndexedFaceSet with the retrieved attributes
	rep = new IndexedFaceSet(cleanVertices, cvSize, 
	                         cleanNormals, cnSize,
	                         marray, meshFrsMaterials.size(),
	                         0, 0,
	                         numFaces, numVertexPerFaces, faceStyle,
	                         cleanVIndices, viSize,
	                         cleanNIndices, niSize,
	                         MIndices, viSize,
	                         0,0,
	                         0);
	// sets the id of the rep
	rep->setId(Id(id, 0));
	
	const BBox<Vec3r> bbox = BBox<Vec3r>(Vec3r(ls.minBBox[0], ls.minBBox[1], ls.minBBox[2]), 
	                                     Vec3r(ls.maxBBox[0], ls.maxBBox[1], ls.maxBBox[2]));
	rep->setBBox(bbox);
	shape->AddRep(rep);
	
	Matrix44r meshMat = Matrix44r::identity();
	currentMesh->setMatrix(meshMat);
	currentMesh->Translate(0,0,0);
	
	currentMesh->AddChild(shape);
	_Scene->AddChild(currentMesh);
	
}

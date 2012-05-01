/* Copyright (c) 2007 Scott Lembcke
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 
#include <stdlib.h>
#include <string.h>

#include "chipmunk_private.h"
#include "chipmunk_unsafe.h"

cpPolyShape *
cpPolyShapeAlloc(void)
{
	return (cpPolyShape *)cpcalloc(1, sizeof(cpPolyShape));
}

static cpBB
cpPolyShapeTransformVerts(cpPolyShape *poly, cpVect p, cpVect rot)
{
	cpVect *src = poly->verts;
	cpVect *dst = poly->tVerts;
	
	cpFloat l = (cpFloat)INFINITY, r = -(cpFloat)INFINITY;
	cpFloat b = (cpFloat)INFINITY, t = -(cpFloat)INFINITY;
	
	for(int i=0; i<poly->numVerts; i++){
		cpVect v = cpvadd(p, cpvrotate(src[i], rot));
		
		dst[i] = v;
		l = cpfmin(l, v.x);
		r = cpfmax(r, v.x);
		b = cpfmin(b, v.y);
		t = cpfmax(t, v.y);
	}
	
	return cpBBNew(l, b, r, t);
}

static void
cpPolyShapeTransformAxes(cpPolyShape *poly, cpVect p, cpVect rot)
{
	cpSplittingPlane *src = poly->planes;
	cpSplittingPlane *dst = poly->tPlanes;
	
	for(int i=0; i<poly->numVerts; i++){
		cpVect n = cpvrotate(src[i].n, rot);
		dst[i].n = n;
		dst[i].d = cpvdot(p, n) + src[i].d;
	}
}

static cpBB
cpPolyShapeCacheData(cpPolyShape *poly, cpVect p, cpVect rot)
{
	cpPolyShapeTransformAxes(poly, p, rot);
	cpBB bb = poly->shape.bb = cpPolyShapeTransformVerts(poly, p, rot);
	
	return bb;
}

static void
cpPolyShapeDestroy(cpPolyShape *poly)
{
	cpfree(poly->verts);
	cpfree(poly->tVerts);
	
	cpfree(poly->planes);
	cpfree(poly->tPlanes);
}

static void
cpPolyShapeNearestPointQuery(cpPolyShape *poly, cpVect p, cpNearestPointQueryInfo *info){
	int count = poly->numVerts;
	cpSplittingPlane *planes = poly->tPlanes;
	cpVect *verts = poly->tVerts;
	
	cpVect v0 = verts[count - 1];
	cpFloat minDist = INFINITY;
	cpVect closestPoint = cpvzero;
	cpBool outside = cpFalse;
	
	for(int i=0; i<count; i++){
		if(cpSplittingPlaneCompare(planes[i], p) > 0.0f) outside = cpTrue;
		
		cpVect v1 = verts[i];
		cpVect closest = cpClosetPointOnSegment(p, v0, v1);
		
		cpFloat dist = cpvdist(p, closest);
		if(dist < minDist){
			minDist = dist;
			closestPoint = closest;
		}
		
		v0 = v1;
	}
	
	info->shape = (cpShape *)poly;
	info->p = closestPoint; // TODO div/0
	info->d = (outside ? minDist : -minDist);
}

static void
cpPolyShapeSegmentQuery(cpPolyShape *poly, cpVect a, cpVect b, cpSegmentQueryInfo *info)
{
	cpSplittingPlane *axes = poly->tPlanes;
	cpVect *verts = poly->tVerts;
	int numVerts = poly->numVerts;
	
	for(int i=0; i<numVerts; i++){
		cpVect n = axes[i].n;
		cpFloat an = cpvdot(a, n);
		if(axes[i].d > an) continue;
		
		cpFloat bn = cpvdot(b, n);
		cpFloat t = (axes[i].d - an)/(bn - an);
		if(t < 0.0f || 1.0f < t) continue;
		
		cpVect point = cpvlerp(a, b, t);
		cpFloat dt = -cpvcross(n, point);
		cpFloat dtMin = -cpvcross(n, verts[i]);
		cpFloat dtMax = -cpvcross(n, verts[(i+1)%numVerts]);
		
		if(dtMin <= dt && dt <= dtMax){
			info->shape = (cpShape *)poly;
			info->t = t;
			info->n = n;
		}
	}
}

static const cpShapeClass polyClass = {
	CP_POLY_SHAPE,
	(cpShapeCacheDataImpl)cpPolyShapeCacheData,
	(cpShapeDestroyImpl)cpPolyShapeDestroy,
	(cpShapeNearestPointQueryImpl)cpPolyShapeNearestPointQuery,
	(cpShapeSegmentQueryImpl)cpPolyShapeSegmentQuery,
};

cpBool
cpPolyValidate(const cpVect *verts, const int numVerts)
{
	for(int i=0; i<numVerts; i++){
		cpVect a = verts[i];
		cpVect b = verts[(i+1)%numVerts];
		cpVect c = verts[(i+2)%numVerts];
		
		if(cpvcross(cpvsub(b, a), cpvsub(c, b)) > 0.0f)
			return cpFalse;
	}
	
	return cpTrue;
}

int
cpPolyShapeGetNumVerts(cpShape *shape)
{
	cpAssertHard(shape->klass == &polyClass, "Shape is not a poly shape.");
	return ((cpPolyShape *)shape)->numVerts;
}

cpVect
cpPolyShapeGetVert(cpShape *shape, int idx)
{
	cpAssertHard(shape->klass == &polyClass, "Shape is not a poly shape.");
	cpAssertHard(0 <= idx && idx < cpPolyShapeGetNumVerts(shape), "Index out of range.");
	
	return ((cpPolyShape *)shape)->verts[idx];
}


static void
setUpVerts(cpPolyShape *poly, int numVerts, cpVect *verts, cpVect offset)
{
	poly->numVerts = numVerts;

	poly->verts = (cpVect *)cpcalloc(numVerts, sizeof(cpVect));
	poly->tVerts = (cpVect *)cpcalloc(numVerts, sizeof(cpVect));
	poly->planes = (cpSplittingPlane *)cpcalloc(numVerts, sizeof(cpSplittingPlane));
	poly->tPlanes = (cpSplittingPlane *)cpcalloc(numVerts, sizeof(cpSplittingPlane));
	
	for(int i=0; i<numVerts; i++){
		cpVect a = cpvadd(offset, verts[i]);
		cpVect b = cpvadd(offset, verts[(i+1)%numVerts]);

		poly->verts[i] = a;
		poly->planes[i] = cpSplittingPlaneNew(a, b);
	}
}

cpPolyShape *
cpPolyShapeInit(cpPolyShape *poly, cpBody *body, int numVerts, cpVect *verts, cpVect offset)
{
	// Fail if the user attempts to pass a concave poly, or a bad winding.
	cpAssertHard(cpPolyValidate(verts, numVerts), "Polygon is concave or has a reversed winding.");
	
	setUpVerts(poly, numVerts, verts, offset);
	cpShapeInit((cpShape *)poly, &polyClass, body);

	return poly;
}

cpShape *
cpPolyShapeNew(cpBody *body, int numVerts, cpVect *verts, cpVect offset)
{
	return (cpShape *)cpPolyShapeInit(cpPolyShapeAlloc(), body, numVerts, verts, offset);
}

cpPolyShape *
cpBoxShapeInit(cpPolyShape *poly, cpBody *body, cpFloat width, cpFloat height)
{
	cpFloat hw = width/2.0f;
	cpFloat hh = height/2.0f;
	
	return cpBoxShapeInit2(poly, body, cpBBNew(-hw, -hh, hw, hh));
}

cpPolyShape *
cpBoxShapeInit2(cpPolyShape *poly, cpBody *body, cpBB box)
{
	cpVect verts[] = {
		cpv(box.l, box.b),
		cpv(box.l, box.t),
		cpv(box.r, box.t),
		cpv(box.r, box.b),
	};
	
	return cpPolyShapeInit(poly, body, 4, verts, cpvzero);
}

cpShape *
cpBoxShapeNew(cpBody *body, cpFloat width, cpFloat height)
{
	return (cpShape *)cpBoxShapeInit(cpPolyShapeAlloc(), body, width, height);
}

cpShape *
cpBoxShapeNew2(cpBody *body, cpBB box)
{
	return (cpShape *)cpBoxShapeInit2(cpPolyShapeAlloc(), body, box);
}

// Unsafe API (chipmunk_unsafe.h)

void
cpPolyShapeSetVerts(cpShape *shape, int numVerts, cpVect *verts, cpVect offset)
{
	cpAssertHard(shape->klass == &polyClass, "Shape is not a poly shape.");
	cpPolyShapeDestroy((cpPolyShape *)shape);
	setUpVerts((cpPolyShape *)shape, numVerts, verts, offset);
}

//MARK: Quick Hull

struct LoopIndexes {int start, end;};

static struct LoopIndexes
QHullLoopIndexes(cpVect *verts, int count)
{
  struct LoopIndexes indexes = {0, 0};
	cpVect min = verts[0];
	cpVect max = min;
	
  for(int i=1; i<count; i++){
    cpVect v = verts[i];
		
    if(v.x < min.x || (v.x == min.x && v.y < min.y)){
      min = v;
      indexes.start = i;
    } else if(v.x > max.x || (v.x == max.x && v.y > max.y)){
			max = v;
			indexes.end = i;
		}
	}
		
  return indexes;
}

#define SWAP(__A__, __B__) {__typeof(__A__) __TMP__ = __A__; __A__ = __B__; __B__ = __TMP__;}

static int
QHullPartition(cpVect *verts, int count, cpSplittingPlane plane, cpFloat tol)
{
	if(count == 0) return 0;
	
	cpFloat max = cpSplittingPlaneCompare(plane, verts[0]);
	int maxi = 0;
	
	int head = 0;
	for(int tail = count-1; head <= tail;){
		cpFloat dist = cpSplittingPlaneCompare(plane, verts[head]);
		if(dist > tol){
			if(dist > max){
				max = dist;
				maxi = head;
			}
			
			head++;
		} else {
			SWAP(verts[head], verts[tail]);
			tail--;
		}
	}
	
	// move the new pivot to the front
	SWAP(verts[0], verts[maxi]);
	
	return head;
}

static int
QHullReduce(cpFloat tol, cpVect *verts, int count, cpVect a, cpVect pivot, cpVect b, cpVect *result)
{
	if(count > 0){
		int left_count = QHullPartition(verts, count, cpSplittingPlaneNew(a, pivot), tol);
		int index = QHullReduce(tol, verts + 1, left_count - 1, a, verts[0], pivot, result);
		
		result[index++] = pivot;
		
		int right_count = QHullPartition(verts + left_count, count - left_count, cpSplittingPlaneNew(pivot, b), tol);
		return index + QHullReduce(tol, verts + left_count + 1, right_count - 1, pivot, verts[left_count], b, result + index);
	} else if(count == 0) {
		result[0] = pivot;
		return 1;
	} else {
		return 0;
	}
}


// QuickHull seemed like a neat algorithm, and efficient-ish for large input sets.
// My implementation performs an in place reduction using the result array as scratch space.
int
cpQuickHull(int count, cpVect *verts, cpFloat tol, cpVect *result, int *first)
{
	// Copy the line vertexes into the empty part of the result polyline to use as a scratch buffer.
	memcpy(result, verts, count*sizeof(cpVect));
	
	// Trivial cases
	if(count <= 2){
		if(first) (*first) = 0;
		return count;
	}
	
	struct LoopIndexes indexes = QHullLoopIndexes(verts, count);
	if(indexes.start == indexes.end){
		if(first) (*first) = 0;
		return 1;
	}
	
	// TODO Why do I push these to the front again? To make scratch space available?
	SWAP(result[0], result[indexes.start]);
	SWAP(result[1], result[indexes.end ?: indexes.start]);
	
	cpVect a = result[0];
	cpVect b = result[1];
	
	result[0] = a;
	if(first) (*first) = indexes.start;
	return QHullReduce(tol, result + 2, count - 2, a, b, a, result + 1) + 1;
}


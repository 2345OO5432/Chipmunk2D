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
 
#include "chipmunk_private.h"

//MARK: Nearest Point Query Functions

struct PointQueryContext {
	cpVect point;
	cpFloat maxDistance;
	cpLayers layers;
	cpGroup group;
	cpSpacePointQueryFunc func;
};

static cpCollisionID
NearestPointQuery(struct PointQueryContext *context, cpShape *shape, cpCollisionID id, void *data)
{
	if(
		!(shape->group && context->group == shape->group) && (context->layers&shape->layers)
	){
		cpPointQueryInfo info;
		cpShapePointQuery(shape, context->point, &info);
		
		if(info.shape && info.distance < context->maxDistance) context->func(shape, info.distance, info.point, data);
	}
	
	return id;
}

void
cpSpacePointQuery(cpSpace *space, cpVect point, cpFloat maxDistance, cpLayers layers, cpGroup group, cpSpacePointQueryFunc func, void *data)
{
	struct PointQueryContext context = {point, maxDistance, layers, group, func};
	cpBB bb = cpBBNewForCircle(point, cpfmax(maxDistance, 0.0f));
	
	cpSpaceLock(space); {
		cpSpatialIndexQuery(space->activeShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQuery, data);
		cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

static cpCollisionID
NearestPointQueryNearest(struct PointQueryContext *context, cpShape *shape, cpCollisionID id, cpPointQueryInfo *out)
{
	if(
		!(shape->group && context->group == shape->group) && (context->layers&shape->layers) && !shape->sensor
	){
		cpPointQueryInfo info;
		cpShapePointQuery(shape, context->point, &info);
		
		if(info.distance < out->distance) (*out) = info;
	}
	
	return id;
}

cpShape *
cpSpacePointQueryNearest(cpSpace *space, cpVect point, cpFloat maxDistance, cpLayers layers, cpGroup group, cpPointQueryInfo *out)
{
	cpPointQueryInfo info = {NULL, cpvzero, maxDistance, cpvzero};
	if(out){
		(*out) = info;
  } else {
		out = &info;
	}
	
	struct PointQueryContext context = {
		point, maxDistance,
		layers, group,
		NULL
	};
	
	cpBB bb = cpBBNewForCircle(point, cpfmax(maxDistance, 0.0f));
	cpSpatialIndexQuery(space->activeShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQueryNearest, out);
	cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)NearestPointQueryNearest, out);
	
	return out->shape;
}


//MARK: Segment Query Functions

struct SegmentQueryContext {
	cpVect start, end;
	cpFloat radius;
	cpLayers layers;
	cpGroup group;
	cpSpaceSegmentQueryFunc func;
};

static cpFloat
SegmentQuery(struct SegmentQueryContext *context, cpShape *shape, void *data)
{
	cpSegmentQueryInfo info;
	
	if(
		!(shape->group && context->group == shape->group) && (context->layers&shape->layers) &&
		cpShapeSegmentQuery(shape, context->start, context->end, context->radius, &info)
	){
		context->func(shape, info.point, info.normal, info.alpha, data);
	}
	
	return 1.0f;
}

void
cpSpaceSegmentQuery(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpLayers layers, cpGroup group, cpSpaceSegmentQueryFunc func, void *data)
{
	struct SegmentQueryContext context = {
		start, end,
		radius,
		layers, group,
		func,
	};
	
	cpSpaceLock(space); {
    cpSpatialIndexSegmentQuery(space->staticShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQuery, data);
    cpSpatialIndexSegmentQuery(space->activeShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

static cpFloat
SegmentQueryFirst(struct SegmentQueryContext *context, cpShape *shape, cpSegmentQueryInfo *out)
{
	cpSegmentQueryInfo info;
	
	if(
		!(shape->group && context->group == shape->group) && (context->layers&shape->layers) &&
		!shape->sensor &&
		cpShapeSegmentQuery(shape, context->start, context->end, context->radius, &info) &&
		info.alpha < out->alpha
	){
		(*out) = info;
	}
	
	return out->alpha;
}

cpShape *
cpSpaceSegmentQueryFirst(cpSpace *space, cpVect start, cpVect end, cpFloat radius, cpLayers layers, cpGroup group, cpSegmentQueryInfo *out)
{
	cpSegmentQueryInfo info = {NULL, end, cpvzero, 1.0f};
	if(out){
		(*out) = info;
  } else {
		out = &info;
	}
	
	struct SegmentQueryContext context = {
		start, end,
		radius,
		layers, group,
		NULL
	};
	
	cpSpatialIndexSegmentQuery(space->staticShapes, &context, start, end, 1.0f, (cpSpatialIndexSegmentQueryFunc)SegmentQueryFirst, out);
	cpSpatialIndexSegmentQuery(space->activeShapes, &context, start, end, out->alpha, (cpSpatialIndexSegmentQueryFunc)SegmentQueryFirst, out);
	
	return out->shape;
}

//MARK: BB Query Functions

struct BBQueryContext {
	cpBB bb;
	cpLayers layers;
	cpGroup group;
	cpSpaceBBQueryFunc func;
};

static cpCollisionID
BBQuery(struct BBQueryContext *context, cpShape *shape, cpCollisionID id, void *data)
{
	if(
		!(shape->group && context->group == shape->group) && (context->layers&shape->layers) &&
		cpBBIntersects(context->bb, shape->bb)
	){
		context->func(shape, data);
	}
	
	return id;
}

void
cpSpaceBBQuery(cpSpace *space, cpBB bb, cpLayers layers, cpGroup group, cpSpaceBBQueryFunc func, void *data)
{
	struct BBQueryContext context = {bb, layers, group, func};
	
	cpSpaceLock(space); {
    cpSpatialIndexQuery(space->activeShapes, &context, bb, (cpSpatialIndexQueryFunc)BBQuery, data);
    cpSpatialIndexQuery(space->staticShapes, &context, bb, (cpSpatialIndexQueryFunc)BBQuery, data);
	} cpSpaceUnlock(space, cpTrue);
}

//MARK: Shape Query Functions

// TODO: Reimplement
//struct ShapeQueryContext {
//	cpSpaceShapeQueryFunc func;
//	void *data;
//	cpBool anyCollision;
//};
//
//// Callback from the spatial hash.
//static cpCollisionID
//ShapeQuery(cpShape *a, cpShape *b, cpCollisionID id, struct ShapeQueryContext *context)
//{
//	// Reject any of the simple cases
//	if(
//		(a->group && a->group == b->group) ||
//		!(a->layers & b->layers) ||
//		a == b
//	) return id;
//	
//	cpContact contacts[CP_MAX_CONTACTS_PER_ARBITER];
//	int numContacts = 0;
//	
//	// Shape 'a' should have the lower shape type. (required by cpCollideShapes() )
//	if(a->klass->type <= b->klass->type){
//		numContacts = cpCollideShapes(a, b, &id, contacts);
//	} else {
//		numContacts = cpCollideShapes(b, a, &id, contacts);
//		for(int i=0; i<numContacts; i++) contacts[i].n = cpvneg(contacts[i].n);
//	}
//	
//	if(numContacts){
//		context->anyCollision = !(a->sensor || b->sensor);
//		
//		if(context->func){
//			cpContactPointSet set;
//			set.count = numContacts;
//			
//			for(int i=0; i<set.count; i++){
//				set.points[i].point = contacts[i].p;
//				set.points[i].normal = contacts[i].n;
//				set.points[i].dist = contacts[i].dist;
//			}
//			
//			context->func(b, &set, context->data);
//		}
//	}
//	
//	return id;
//}
//
//cpBool
//cpSpaceShapeQuery(cpSpace *space, cpShape *shape, cpSpaceShapeQueryFunc func, void *data)
//{
//	cpBody *body = shape->body;
//	cpBB bb = (body ? cpShapeUpdate(shape, body->p, body->rot) : shape->bb);
//	struct ShapeQueryContext context = {func, data, cpFalse};
//	
//	cpSpaceLock(space); {
//    cpSpatialIndexQuery(space->activeShapes, shape, bb, (cpSpatialIndexQueryFunc)ShapeQuery, &context);
//    cpSpatialIndexQuery(space->staticShapes, shape, bb, (cpSpatialIndexQueryFunc)ShapeQuery, &context);
//	} cpSpaceUnlock(space, cpTrue);
//	
//	return context.anyCollision;
//}
